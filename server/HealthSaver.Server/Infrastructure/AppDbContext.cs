using HealthSaver.Server.Models;
using Microsoft.EntityFrameworkCore;

namespace HealthSaver.Server.Infrastructure;

public sealed class AppDbContext : DbContext
{
    public AppDbContext(DbContextOptions<AppDbContext> options) : base(options)
    {
    }

    public DbSet<Device> Devices => Set<Device>();
    public DbSet<SensorType> SensorTypes => Set<SensorType>();
    public DbSet<Measurement> Measurements => Set<Measurement>();
    public DbSet<MeasurementChunk> MeasurementChunks => Set<MeasurementChunk>();

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<Device>(entity =>
        {
            entity.HasIndex(e => e.DeviceId).IsUnique();
            entity.Property(e => e.DeviceId).HasMaxLength(100);
        });

        modelBuilder.Entity<SensorType>(entity =>
        {
            entity.HasIndex(e => new { e.Code, e.SchemaVersion }).IsUnique();
            entity.Property(e => e.Code).HasMaxLength(50);
            entity.Property(e => e.Unit).HasMaxLength(20);
        });

        modelBuilder.Entity<Measurement>(entity =>
        {
            entity.HasIndex(e => e.StartTimeUtc);
            entity.Property(e => e.Unit).HasMaxLength(20);
            entity.Property(e => e.Status).HasMaxLength(20);
            entity.Property(e => e.RawFilePath).HasMaxLength(300);
        });

        modelBuilder.Entity<MeasurementChunk>(entity =>
        {
            entity.HasIndex(e => new { e.MeasurementId, e.ChunkIndex }).IsUnique();
            entity.Property(e => e.Sha256).HasMaxLength(64);
        });
    }
}
